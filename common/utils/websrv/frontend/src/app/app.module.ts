import { DragDropModule } from '@angular/cdk/drag-drop';
import { HttpClientModule } from '@angular/common/http';
import { NgModule } from '@angular/core';
import { MatSliderModule } from '@angular/material/slider';
import { FlexLayoutModule } from '@angular/flex-layout';
import { FormsModule } from '@angular/forms';
import { ReactiveFormsModule } from '@angular/forms';
import { MatButtonModule } from '@angular/material/button';
import { MatCardModule } from '@angular/material/card';
import { MatChipsModule } from '@angular/material/chips';
import { MatDialogModule } from '@angular/material/dialog';
import { MatFormFieldModule } from '@angular/material/form-field';
import { MatGridListModule } from '@angular/material/grid-list';
import { MatInputModule } from '@angular/material/input';
import { MatProgressSpinnerModule } from '@angular/material/progress-spinner';
import { MatSelectModule } from '@angular/material/select';
import { MatSlideToggleModule } from '@angular/material/slide-toggle';
import { MatSnackBarModule } from '@angular/material/snack-bar';
import { MatTableModule } from '@angular/material/table';
import { MatTabsModule } from '@angular/material/tabs';
import { MatToolbarModule } from '@angular/material/toolbar';
import { MatListModule} from '@angular/material/list';
import { BrowserModule } from '@angular/platform-browser';
import { BrowserAnimationsModule } from '@angular/platform-browser/animations';
import { CommandsApi } from './api/commands.api';
import { ScopeApi } from './api/scope.api';
import { AppRoutingModule } from './app-routing.module';
import { AppComponent } from './app.component';
import { CommandsComponent } from './components/commands/commands.component';
import { ConfirmDialogComponent } from './components/confirm/confirm.component';
import { DialogComponent } from './components/dialog/dialog.component';
import { QuestionDialogComponent } from './components/question/question.component';
import { ScopeComponent } from './components/scope/scope.component';
import { InterceptorProviders } from './interceptors/interceptors';
import { LoadingService } from './services/loading.service';
import { WebSocketService } from './services/websocket.service';
import { NgChartsModule } from 'ng2-charts';
import { CommonModule } from '@angular/common';

@NgModule({
    declarations: [
        AppComponent,
        CommandsComponent,
        ConfirmDialogComponent,
        QuestionDialogComponent,
        DialogComponent,
        ScopeComponent
    ],
    imports: [
        BrowserModule,
        AppRoutingModule,
        FormsModule,
        ReactiveFormsModule ,
        BrowserAnimationsModule,
        HttpClientModule,
        MatButtonModule,
        FlexLayoutModule,
        MatDialogModule,
        DragDropModule,
        MatSliderModule,
        MatFormFieldModule,
        MatInputModule,
        MatChipsModule,
        MatProgressSpinnerModule,
        MatToolbarModule,
        MatTableModule,
        MatListModule,
        MatSelectModule,
        MatSnackBarModule,
        MatSlideToggleModule,
        MatGridListModule,
        MatCardModule,
        MatTabsModule,
        NgChartsModule,
    ],
    providers: [
        // services
        LoadingService,
        WebSocketService,
        // api
        CommandsApi,
        ScopeApi,
        // interceptors
        InterceptorProviders,
    ],
    bootstrap: [AppComponent]
})
export class AppModule { }
